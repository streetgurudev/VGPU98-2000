/*
 * mkdisk: writes a 1.44 MB FAT12 floppy image holding the given files in its
 * root directory, for installing the driver from a virtual floppy.
 *
 *   mkdisk -o IMAGE -l LABEL -d YYYY-MM-DD FILE...
 *
 * The image depends only on the inputs: every timestamp is the given date at
 * midnight, the volume serial number is derived from the label and date, and
 * files are stored contiguously in the order given. Booting the disk prints a
 * message and waits for a key. Layout: spec/driver-package.md.
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

#define SECTOR        512
#define SECTORS       2880 /* 80 tracks, 2 heads, 18 sectors */
#define FAT_SECTORS   9
#define ROOT_ENTRIES  224
#define ROOT_SECTORS  (ROOT_ENTRIES * 32 / SECTOR)
#define FIRST_DATA    (1 + 2 * FAT_SECTORS + ROOT_SECTORS) /* sector 33 */
#define CLUSTERS      (SECTORS - FIRST_DATA)                 /* 2847, one sector each */

static uint8_t img[SECTORS * SECTOR];

static void
put16(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t) v;
    p[1] = (uint8_t) (v >> 8);
}

static void
put32(uint8_t *p, uint32_t v)
{
    put16(p, v & 0xffff);
    put16(p + 2, v >> 16);
}

static void
fat_set(uint32_t cluster, uint32_t value)
{
    int      f;
    uint32_t at = cluster * 3 / 2;

    for (f = 0; f < 2; f++) {
        uint8_t *fat = img + SECTOR * (1 + f * FAT_SECTORS);

        if (cluster & 1) {
            fat[at]     = (uint8_t) ((fat[at] & 0x0f) | ((value & 0x0f) << 4));
            fat[at + 1] = (uint8_t) (value >> 4);
        } else {
            fat[at]     = (uint8_t) value;
            fat[at + 1] = (uint8_t) ((fat[at + 1] & 0xf0) | ((value >> 8) & 0x0f));
        }
    }
}

/* An 8.3 directory name from a file name, in upper case. */
static void
dos_name(const char *path, uint8_t out[11])
{
    const char *base = path;
    const char *p;
    const char *dot;
    size_t      n, i;

    for (p = path; *p; p++) {
        if ((*p == '/') || (*p == '\\'))
            base = p + 1;
    }
    dot = strrchr(base, '.');
    n   = dot ? (size_t) (dot - base) : strlen(base);
    if ((n == 0) || (n > 8) || (dot && (strlen(dot + 1) > 3)))
        die("%s: not an 8.3 file name", base);
    memset(out, ' ', 11);
    for (i = 0; i < n; i++)
        out[i] = (uint8_t) toupper((unsigned char) base[i]);
    if (dot) {
        for (i = 0; dot[1 + i]; i++)
            out[8 + i] = (uint8_t) toupper((unsigned char) dot[1 + i]);
    }
    for (i = 0; i < 11; i++) {
        if ((out[i] < 0x21 && out[i] != ' ') || (out[i] > 0x7e) || strchr("\"*+,./:;<=>?[\\]|", out[i]))
            die("%s: character '%c' is not allowed in a DOS file name", base, out[i]);
    }
}

/* Boot sector: a BIOS parameter block for a 1.44 MB disk, and code that says
   the disk does not start anything. */
static void
boot_sector(const char *label, uint32_t serial)
{
    static const char    msg[] = "\r\nThis is the VGPU98 driver disk; it does not start a system.\r\n"
                                 "Remove it and press a key.\r\n";
    static const uint8_t code[] = {
        0xfa,             /* cli */
        0x31, 0xc0,       /* xor ax, ax */
        0x8e, 0xd0,       /* mov ss, ax */
        0xbc, 0x00, 0x7c, /* mov sp, 7C00h */
        0x8e, 0xd8,       /* mov ds, ax */
        0xfb,             /* sti */
        0xbe, 0x00, 0x00, /* mov si, msg (patched) */
        0xac,             /* .next: lodsb */
        0x08, 0xc0,       /* or al, al */
        0x74, 0x09,       /* jz .key */
        0xb4, 0x0e,       /* mov ah, 0Eh */
        0xbb, 0x07, 0x00, /* mov bx, 7 */
        0xcd, 0x10,       /* int 10h */
        0xeb, 0xf2,       /* jmp .next */
        0x31, 0xc0,       /* .key: xor ax, ax */
        0xcd, 0x16,       /* int 16h */
        0xcd, 0x19        /* int 19h: try the next boot device */
    };
    uint8_t *b = img;
    size_t   n = strlen(label);
    size_t   msg_at;

    b[0] = 0xeb; /* jmp short to the code */
    b[1] = 0x3c;
    b[2] = 0x90;
    memcpy(b + 3, "VGPU98  ", 8);
    put16(b + 11, SECTOR);
    b[13] = 1;                     /* sectors per cluster */
    put16(b + 14, 1);              /* reserved sectors */
    b[16] = 2;                     /* FATs */
    put16(b + 17, ROOT_ENTRIES);
    put16(b + 19, SECTORS);
    b[21] = 0xf0;                  /* media: 1.44 MB */
    put16(b + 22, FAT_SECTORS);
    put16(b + 24, 18);             /* sectors per track */
    put16(b + 26, 2);              /* heads */
    put32(b + 28, 0);              /* hidden sectors */
    put32(b + 32, 0);              /* large sector count */
    b[36] = 0x00;                  /* drive number */
    b[38] = 0x29;                  /* extended boot signature */
    put32(b + 39, serial);
    memset(b + 43, ' ', 11);
    memcpy(b + 43, label, n);
    memcpy(b + 54, "FAT12   ", 8);
    memcpy(b + 62, code, sizeof(code));
    msg_at = 62 + sizeof(code);
    memcpy(b + msg_at, msg, sizeof(msg));
    put16(b + 62 + 12, (uint32_t) (0x7c00 + msg_at)); /* mov si, msg */
    b[510] = 0x55;
    b[511] = 0xaa;
}

int
main(int argc, char **argv)
{
    const char *out_path = NULL;
    const char *label    = NULL;
    const char *date     = NULL;
    unsigned    year, month, day;
    char        tail;
    uint32_t    fat_date;
    uint32_t    serial = 0;
    uint32_t    next   = 2; /* first free cluster */
    uint8_t    *dir    = img + SECTOR * (1 + 2 * FAT_SECTORS);
    int         n_entries = 0;
    int         i;
    size_t      k;

    set_progname("mkdisk");
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && (i + 1 < argc))
            out_path = argv[++i];
        else if (!strcmp(argv[i], "-l") && (i + 1 < argc))
            label = argv[++i];
        else if (!strcmp(argv[i], "-d") && (i + 1 < argc))
            date = argv[++i];
        else if (argv[i][0] == '-')
            die("usage: mkdisk -o IMAGE -l LABEL -d YYYY-MM-DD FILE...");
        else
            break;
    }
    if (!out_path || !label || !date || (i >= argc))
        die("usage: mkdisk -o IMAGE -l LABEL -d YYYY-MM-DD FILE...");
    if ((strlen(label) == 0) || (strlen(label) > 11))
        die("the volume label has 1 to 11 characters");
    for (k = 0; label[k]; k++) {
        if (!isupper((unsigned char) label[k]) && !isdigit((unsigned char) label[k]) && (label[k] != '_') &&
            (label[k] != '-'))
            die("the volume label takes upper-case letters, digits, '_' and '-'");
    }
    if ((sscanf(date, "%u-%u-%u%c", &year, &month, &day, &tail) != 3) || (year < 1980) || (year > 2107) ||
        (month < 1) || (month > 12) || (day < 1) || (day > 31))
        die("the date is YYYY-MM-DD, from 1980 to 2107");
    fat_date = ((year - 1980) << 9) | (month << 5) | day;

    /* The serial number: a hash of the label and the date, so it is reproducible. */
    for (k = 0; label[k]; k++)
        serial = serial * 31 + (uint8_t) label[k];
    serial = serial * 0x9e3779b1u + fat_date;

    memset(img, 0xf6, sizeof(img)); /* the format filler byte */
    memset(img, 0, SECTOR * FIRST_DATA);
    boot_sector(label, serial);
    fat_set(0, 0xff0); /* media byte */
    fat_set(1, 0xfff);

    /* Volume label entry. */
    memset(dir, ' ', 11);
    memcpy(dir, label, strlen(label));
    dir[11] = 0x08;
    put16(dir + 22, 0);
    put16(dir + 24, fat_date);
    n_entries = 1;

    for (; i < argc; i++) {
        size_t   len;
        uint8_t *data = read_file(argv[i], &len);
        uint8_t *e    = dir + 32 * n_entries;
        uint32_t clusters = (uint32_t) ((len + SECTOR - 1) / SECTOR);
        uint32_t c;
        int      j;

        if (n_entries >= ROOT_ENTRIES)
            die("too many files for the root directory");
        dos_name(argv[i], e);
        for (j = 1; j < n_entries; j++) {
            if (!memcmp(dir + 32 * j, e, 11))
                die("%s: two files with the same DOS name", argv[i]);
        }
        if (next + clusters > CLUSTERS + 2)
            die("the files do not fit on a 1.44 MB disk");
        e[11] = 0x20; /* archive */
        put16(e + 22, 0);
        put16(e + 24, fat_date);
        put16(e + 26, len ? next : 0);
        put32(e + 28, (uint32_t) len);
        for (c = 0; c < clusters; c++) {
            uint8_t *sector = img + SECTOR * (FIRST_DATA + next + c - 2);
            size_t   from   = (size_t) c * SECTOR;
            size_t   n      = (len - from < SECTOR) ? len - from : SECTOR;

            memset(sector, 0, SECTOR);
            memcpy(sector, data + from, n);
            fat_set(next + c, (c + 1 < clusters) ? next + c + 1 : 0xfff);
        }
        next += clusters;
        n_entries++;
        free(data);
    }

    write_file(out_path, img, sizeof(img));
    return 0;
}

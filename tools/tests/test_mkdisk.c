/*
 * Tests for mkdisk: reads the image back with a FAT12 reader written here and
 * checks the boot sector, the label, the timestamps and every file's bytes.
 *
 * Usage: test_mkdisk IMAGE LABEL YYYY-MM-DD FILE...
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "util.h"

static uint8_t *img;

static uint32_t
u16(size_t at)
{
    return img[at] | ((uint32_t) img[at + 1] << 8);
}

static uint32_t
u32(size_t at)
{
    return u16(at) | (u16(at + 2) << 16);
}

static uint32_t
fat_next(uint32_t cluster, size_t fat)
{
    size_t   at = fat + cluster * 3 / 2;
    uint32_t v  = u16(at);

    return (cluster & 1) ? (v >> 4) : (v & 0x0fff);
}

int
main(int argc, char **argv)
{
    size_t   size;
    size_t   bps, fat, root, data;
    uint32_t spc, nfats, nroot, spf;
    unsigned year, month, day;
    uint32_t want_date;
    int      i;

    set_progname("test_mkdisk");
    if (argc < 5)
        die("usage: test_mkdisk IMAGE LABEL YYYY-MM-DD FILE...");
    img = read_file(argv[1], &size);
    CHECK(size == 1474560);
    if (size != 1474560)
        CHECK_DONE();
    if (sscanf(argv[3], "%u-%u-%u", &year, &month, &day) != 3)
        die("bad date");
    want_date = ((year - 1980) << 9) | (month << 5) | day;

    /* Boot sector and BIOS parameter block of a 1.44 MB disk. */
    CHECK((img[0] == 0xeb) && (img[2] == 0x90));
    CHECK((img[510] == 0x55) && (img[511] == 0xaa));
    bps   = u16(11);
    spc   = img[13];
    nfats = img[16];
    nroot = u16(17);
    spf   = u16(22);
    CHECK((bps == 512) && (spc == 1) && (u16(14) == 1) && (nfats == 2) && (nroot == 224));
    CHECK((u16(19) == 2880) && (img[21] == 0xf0) && (spf == 9) && (u16(24) == 18) && (u16(26) == 2));
    CHECK((img[38] == 0x29) && (memcmp(img + 54, "FAT12   ", 8) == 0));
    CHECK((memcmp(img + 43, argv[2], strlen(argv[2])) == 0) && (img[43 + strlen(argv[2])] == ' '));
    /* The boot code says what the disk is: its message is in the sector. */
    {
        int found = 0;
        size_t k;

        for (k = 62; k + 6 < 510; k++)
            found |= !memcmp(img + k, "VGPU98", 6);
        CHECK(found);
    }

    fat  = bps * 1;
    root = fat + bps * spf * nfats;
    data = root + 32 * nroot;
    CHECK(memcmp(img + fat, img + fat + bps * spf, bps * spf) == 0); /* both FATs agree */
    CHECK((u16(fat) & 0x0fff) == 0xff0);

    /* Root directory: the label, then the files in order. */
    CHECK((img[root + 11] == 0x08) && (memcmp(img + root, argv[2], strlen(argv[2])) == 0));
    for (i = 4; i < argc; i++) {
        size_t   e = root + 32 * (size_t) (i - 3);
        size_t   len;
        uint8_t *want = read_file(argv[i], &len);
        uint8_t  name[11];
        const char *base = argv[i];
        const char *p;
        const char *dot;
        size_t   k, n, got = 0;
        uint32_t c;
        int      ok = 1;

        for (p = argv[i]; *p; p++) {
            if ((*p == '/') || (*p == '\\'))
                base = p + 1;
        }
        memset(name, ' ', 11);
        dot = strrchr(base, '.');
        n   = dot ? (size_t) (dot - base) : strlen(base);
        for (k = 0; k < n; k++)
            name[k] = (uint8_t) toupper((unsigned char) base[k]);
        for (k = 0; dot && dot[1 + k]; k++)
            name[8 + k] = (uint8_t) toupper((unsigned char) dot[1 + k]);
        CHECK(memcmp(img + e, name, 11) == 0);
        CHECK((img[e + 11] == 0x20) && (u16(e + 22) == 0) && (u16(e + 24) == want_date));
        CHECK(u32(e + 28) == len);

        /* Follow the cluster chain. */
        c = u16(e + 26);
        while ((c >= 2) && (c < 0xff8) && (got < len)) {
            size_t sector = data + (c - 2) * bps * spc;
            size_t take   = (len - got < bps) ? len - got : bps;

            ok &= !memcmp(img + sector, want + got, take);
            got += take;
            c = fat_next(c, fat);
        }
        CHECK(ok && (got == len) && (len == 0 || c >= 0xff8));
        free(want);
    }
    CHECK(img[root + 32 * (argc - 3)] == 0); /* nothing after the last file */
    free(img);
    CHECK_DONE();
}

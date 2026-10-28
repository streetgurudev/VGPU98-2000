/*
 * romfix: pads a raw option ROM image to its final size and sets the checksum byte.
 *
 * The last byte of the image is reserved for the checksum, which makes all bytes
 * sum to zero (mod 256). The raw image must therefore be at least one byte smaller
 * than the final size. Padding bytes are zero.
 *
 * Usage: romfix RAW OUT --card FILE   (size from [bios] rom_size)
 *        romfix RAW OUT --size N
 *
 * Copyright 2026 VGPU98 Project contributors
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cfg.h"
#include "util.h"

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
    const char *raw_path = NULL;
    const char *out_path = NULL;
    uint32_t    size     = 0;
    size_t      raw_len;
    uint8_t    *raw;
    uint8_t    *rom;
    uint8_t     sum = 0;
    uint32_t    i;
    int         a;

    set_progname("romfix");
    for (a = 1; a < argc; a++) {
        if ((a + 1 < argc) && (strcmp(argv[a], "--card") == 0)) {
            cfg_t         *cfg = cfg_load(argv[++a]);
            cfg_section_t *sec = cfg_section(cfg, "bios");

            size = cfg_get_u32(cfg, sec, "rom_size", 512, 0x20000);
            cfg_free(cfg);
        } else if ((a + 1 < argc) && (strcmp(argv[a], "--size") == 0)) {
            if (!parse_u32(argv[++a], &size) || (size < 512) || (size % 512) || (size > 0x20000))
                die("--size must be a multiple of 512 between 512 and 128 KB");
        } else if ((argv[a][0] != '-') && (raw_path == NULL))
            raw_path = argv[a];
        else if ((argv[a][0] != '-') && (out_path == NULL))
            out_path = argv[a];
        else
            die("usage: romfix RAW OUT --card FILE | --size N");
    }
    if (!raw_path || !out_path || !size || (size % 512))
        die("usage: romfix RAW OUT --card FILE | --size N");

    raw = read_file(raw_path, &raw_len);
    if (raw_len >= size)
        die("%s is %u bytes; it must be at most %u (the last byte holds the checksum)", raw_path,
            (unsigned) raw_len, size - 1);

    rom = xcalloc(1, size);
    memcpy(rom, raw, raw_len);
    for (i = 0; i < size - 1; i++)
        sum = (uint8_t) (sum + rom[i]);
    rom[size - 1] = (uint8_t) (0x100 - sum);

    write_file(out_path, rom, size);
    printf("%s: %u bytes, %u used, %u free\n", base_name(out_path), size, (unsigned) raw_len + 1,
           size - (unsigned) raw_len - 1);

    free(rom);
    free(raw);
    return 0;
}
